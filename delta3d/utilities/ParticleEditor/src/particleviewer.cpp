#include <particleviewer.h>

#include <osg/BlendFunc>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/PrimitiveSet>
#include <osg/StateAttribute>
#include <osg/Image>
#include <osg/Material>
#include <osg/MatrixTransform>
#include <osg/Texture2D>

#include <osgParticle/AccelOperator>
#include <osgParticle/FluidFrictionOperator>
#include <osgParticle/ForceOperator>
#include <osgParticle/MultiSegmentPlacer>
#include <osgParticle/PointPlacer>
#include <osgParticle/RadialShooter>
#include <osgParticle/RandomRateCounter>
#include <osgParticle/SectorPlacer>
#include <osgParticle/SegmentPlacer>

#include <osgViewer/GraphicsWindow>
#include <osgViewer/CompositeViewer>

#include <dtCore/refptr.h>
#include <dtCore/system.h>
#include <dtCore/globals.h>
#include <dtCore/transform.h>
#include <dtCore/scene.h>
#include <dtCore/camera.h>
#include <dtCore/light.h>
#include <dtCore/compass.h>

////////////////////////////////////////////////////////////////////////////////
ParticleViewer::ParticleViewer()
: mpParticleSystemGroup(NULL)
, mpParticleSystemUpdater(NULL)
, mLayerIndex(0)
{
}

////////////////////////////////////////////////////////////////////////////////
ParticleViewer::~ParticleViewer()
{
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::Config()
{
   dtABC::Application::Config();
   GetCompositeViewer()->setThreadingModel(osgViewer::CompositeViewer::SingleThreaded);

   mMotion = new OrbitMotionModel(GetMouse(), GetCamera());

   MakeCompass();
   MakeGrids();

   GetScene()->GetSceneNode()->addChild(mpXYGridTransform);
   GetScene()->GetSceneNode()->addChild(mpXZGridTransform);
   GetScene()->GetSceneNode()->addChild(mpYZGridTransform);

   mMotion->SetCompassTransform(mpCompassTransform);

   CreateNewParticleSystem();
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::CreateNewParticleSystem()
{
   if(mpParticleSystemGroup != NULL)
   {
      GetScene()->GetSceneNode()->removeChild(mpParticleSystemGroup);
   }

   mpParticleSystemGroup = new osg::Group();
   mpParticleSystemUpdater = new osgParticle::ParticleSystemUpdater();
   mpParticleSystemGroup->addChild(mpParticleSystemUpdater);
   GetScene()->GetSceneNode()->addChild(mpParticleSystemGroup);
   //setParticleSystemFilename("");
   mLayers.clear();
   //UpdateLayers();
   CreateNewParticleLayer();
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::CreateNewParticleLayer()
{
   ParticleSystemLayer layer;
   layer.mParticleSystem = new osgParticle::ParticleSystem();
   layer.mParticleSystem->setDefaultAttributes("", true, false);
   layer.mpParticle = new osgParticle::Particle();
   layer.mParticleSystem->setDefaultParticleTemplate(*layer.mpParticle);
   layer.mGeode = new osg::Geode();

   char buf[256];
   sprintf(buf, "Layer %u", unsigned(mLayers.size()));
   layer.mGeode->setName(buf);
   layer.mGeode->addDrawable(layer.mParticleSystem.get());
   mpParticleSystemGroup->addChild(layer.mGeode.get());

   layer.mEmitterTransform = new osg::MatrixTransform();
   layer.mModularEmitter = new osgParticle::ModularEmitter();
   layer.mModularEmitter->setParticleSystem(layer.mParticleSystem.get());
   osgParticle::RandomRateCounter* rrc = new osgParticle::RandomRateCounter();
   rrc->setRateRange(20, 30);
   layer.mModularEmitter->setCounter(rrc);
   layer.mModularEmitter->setPlacer(new osgParticle::PointPlacer());
   layer.mModularEmitter->setShooter(new osgParticle::RadialShooter());
   layer.mModularEmitter->setLifeTime(5.0);
   layer.mEmitterTransform->addChild(layer.mModularEmitter.get());
   mpParticleSystemGroup->addChild(layer.mEmitterTransform.get());

   layer.mModularProgram = new osgParticle::ModularProgram();
   layer.mModularProgram->setParticleSystem(layer.mParticleSystem.get());
   mpParticleSystemGroup->addChild(layer.mModularProgram.get());
   mpParticleSystemUpdater->addParticleSystem(layer.mParticleSystem.get());
   mLayers.push_back(layer);

   ResetEmitters();
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::DeleteSelectedLayer()
{
   mpParticleSystemGroup->removeChild(mLayers[mLayerIndex].mGeode.get());
   mpParticleSystemGroup->removeChild(mLayers[mLayerIndex].mEmitterTransform.get());
   mpParticleSystemGroup->removeChild(mLayers[mLayerIndex].mModularProgram.get());

   mpParticleSystemUpdater->removeParticleSystem(mLayers[mLayerIndex].mParticleSystem.get());

   mLayers.erase(mLayers.begin() + mLayerIndex);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::ToggleSelectedLayerHidden()
{
   if(mLayers[mLayerIndex].mModularEmitter->isEnabled())
   {
      mLayers[mLayerIndex].mModularEmitter->setEnabled(false);
   }
   else
   {
      mLayers[mLayerIndex].mModularEmitter->setEnabled(true);
   }
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::ResetEmitters()
{
   for (unsigned int i = 0; i < mLayers.size(); ++i)
   {
      mLayers[i].mModularEmitter->setCurrentTime(0.0);
      mLayers[i].mModularProgram->setCurrentTime(0.0);
   }
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::UpdateSelectionIndex(int newIndex)
{
   mLayerIndex = newIndex;
   if(0 <= mLayerIndex && mLayerIndex < static_cast<int>(mLayers.size()))
   {
      UpdateParticleTabsValues();
      UpdateCounterTabsValues();
      UpdatePlacerTabsValues();
   }
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::AlignmentChanged(int newAlignment)
{
   mLayers[mLayerIndex].mParticleSystem->setParticleAlignment(
      (osgParticle::ParticleSystem::Alignment)newAlignment);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::ShapeChanged(int newShape)
{
   mLayers[mLayerIndex].mpParticle->setShape((osgParticle::Particle::Shape)newShape);
   mLayers[mLayerIndex].mParticleSystem->setDefaultParticleTemplate(*mLayers[mLayerIndex].mpParticle);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::ToggleEmissive(bool enabled)
{
   std::string textureFile = "";
   osg::StateSet* ss = mLayers[mLayerIndex].mParticleSystem->getStateSet();
   osg::StateAttribute* sa = ss->getTextureAttribute(0, osg::StateAttribute::TEXTURE);
   if(IS_A(sa, osg::Texture2D*))
   {
      osg::Texture2D* t2d = (osg::Texture2D*)sa;
      osg::Image* image = t2d->getImage();
      if (image != NULL)
      {
         textureFile = image->getFileName();
      }
   }
   osg::Material* material = (osg::Material*)ss->getAttribute(osg::StateAttribute::MATERIAL);
   bool lighting = material->getColorMode() == osg::Material::AMBIENT_AND_DIFFUSE;

   mLayers[mLayerIndex].mParticleSystem->setDefaultAttributes(textureFile, enabled, lighting);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::ToggleLighting(bool enabled)
{
   std::string textureFile = "";
   osg::StateSet* ss = mLayers[mLayerIndex].mParticleSystem->getStateSet();
   osg::StateAttribute* sa = ss->getTextureAttribute(0, osg::StateAttribute::TEXTURE);
   if(IS_A(sa, osg::Texture2D*))
   {
      osg::Texture2D* t2d = (osg::Texture2D*)sa;
      osg::Image* image = t2d->getImage();
      if (image != NULL)
      {
         textureFile = image->getFileName();
      }
   }
   osg::BlendFunc* blend = (osg::BlendFunc*)ss->getAttribute(osg::StateAttribute::BLENDFUNC);
   bool emissive = blend->getDestination() == osg::BlendFunc::ONE;

   mLayers[mLayerIndex].mParticleSystem->setDefaultAttributes(textureFile, emissive, enabled);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::LifeValueChanged(double newValue)
{
   mLayers[mLayerIndex].mpParticle->setLifeTime(newValue);
   mLayers[mLayerIndex].mParticleSystem->setDefaultParticleTemplate(*mLayers[mLayerIndex].mpParticle);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::RadiusValueChanged(double newValue)
{
   mLayers[mLayerIndex].mpParticle->setRadius(newValue);
   mLayers[mLayerIndex].mParticleSystem->setDefaultParticleTemplate(*mLayers[mLayerIndex].mpParticle);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::MassValueChanged(double newValue)
{
   mLayers[mLayerIndex].mpParticle->setMass(newValue);
   mLayers[mLayerIndex].mParticleSystem->setDefaultParticleTemplate(*mLayers[mLayerIndex].mpParticle);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::SizeFromValueChanged(double newValue)
{
   osgParticle::rangef sizeRange = mLayers[mLayerIndex].mpParticle->getSizeRange();
   sizeRange.minimum = newValue;
   mLayers[mLayerIndex].mpParticle->setSizeRange(sizeRange);
   mLayers[mLayerIndex].mParticleSystem->setDefaultParticleTemplate(*mLayers[mLayerIndex].mpParticle);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::SizeToValueChanged(double newValue)
{
   osgParticle::rangef sizeRange = mLayers[mLayerIndex].mpParticle->getSizeRange();
   sizeRange.maximum = newValue;
   mLayers[mLayerIndex].mpParticle->setSizeRange(sizeRange);
   mLayers[mLayerIndex].mParticleSystem->setDefaultParticleTemplate(*mLayers[mLayerIndex].mpParticle);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::TextureChanged(QString filename, bool emissive, bool lighting)
{
   mLayers[mLayerIndex].mParticleSystem->setDefaultAttributes(filename.toStdString(), emissive, lighting);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::RFromValueChanged(double newValue)
{
   osgParticle::rangev4 colorRange = mLayers[mLayerIndex].mpParticle->getColorRange();
   colorRange.minimum[0] = newValue;
   mLayers[mLayerIndex].mpParticle->setColorRange(colorRange);
   mLayers[mLayerIndex].mParticleSystem->setDefaultParticleTemplate(*mLayers[mLayerIndex].mpParticle);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::RToValueChanged(double newValue)
{
   osgParticle::rangev4 colorRange = mLayers[mLayerIndex].mpParticle->getColorRange();
   colorRange.maximum[0] = newValue;
   mLayers[mLayerIndex].mpParticle->setColorRange(colorRange);
   mLayers[mLayerIndex].mParticleSystem->setDefaultParticleTemplate(*mLayers[mLayerIndex].mpParticle);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::GFromValueChanged(double newValue)
{
   osgParticle::rangev4 colorRange = mLayers[mLayerIndex].mpParticle->getColorRange();
   colorRange.minimum[1] = newValue;
   mLayers[mLayerIndex].mpParticle->setColorRange(colorRange);
   mLayers[mLayerIndex].mParticleSystem->setDefaultParticleTemplate(*mLayers[mLayerIndex].mpParticle);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::GToValueChanged(double newValue)
{
   osgParticle::rangev4 colorRange = mLayers[mLayerIndex].mpParticle->getColorRange();
   colorRange.maximum[1] = newValue;
   mLayers[mLayerIndex].mpParticle->setColorRange(colorRange);
   mLayers[mLayerIndex].mParticleSystem->setDefaultParticleTemplate(*mLayers[mLayerIndex].mpParticle);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::BFromValueChanged(double newValue)
{
   osgParticle::rangev4 colorRange = mLayers[mLayerIndex].mpParticle->getColorRange();
   colorRange.minimum[2] = newValue;
   mLayers[mLayerIndex].mpParticle->setColorRange(colorRange);
   mLayers[mLayerIndex].mParticleSystem->setDefaultParticleTemplate(*mLayers[mLayerIndex].mpParticle);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::BToValueChanged(double newValue)
{
   osgParticle::rangev4 colorRange = mLayers[mLayerIndex].mpParticle->getColorRange();
   colorRange.maximum[2] = newValue;
   mLayers[mLayerIndex].mpParticle->setColorRange(colorRange);
   mLayers[mLayerIndex].mParticleSystem->setDefaultParticleTemplate(*mLayers[mLayerIndex].mpParticle);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::AFromValueChanged(double newValue)
{
   osgParticle::rangef sizeRange = mLayers[mLayerIndex].mpParticle->getAlphaRange();
   sizeRange.minimum = newValue;
   mLayers[mLayerIndex].mpParticle->setAlphaRange(sizeRange);
   mLayers[mLayerIndex].mParticleSystem->setDefaultParticleTemplate(*mLayers[mLayerIndex].mpParticle);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::AToValueChanged(double newValue)
{
   osgParticle::rangef sizeRange = mLayers[mLayerIndex].mpParticle->getAlphaRange();
   sizeRange.maximum = newValue;
   mLayers[mLayerIndex].mpParticle->setAlphaRange(sizeRange);
   mLayers[mLayerIndex].mParticleSystem->setDefaultParticleTemplate(*mLayers[mLayerIndex].mpParticle);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::EmitterLifeValueChanged(double newValue)
{
   mLayers[mLayerIndex].mModularEmitter->setLifeTime(newValue);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::EmitterStartValueChanged(double newValue)
{
   mLayers[mLayerIndex].mModularEmitter->setStartTime(newValue);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::EmitterResetValueChanged(double newValue)
{
   mLayers[mLayerIndex].mModularEmitter->setResetTime(newValue);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::EndlessLifetimeChanged(bool endless)
{
   mLayers[mLayerIndex].mModularEmitter->setEndless(endless);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::CounterTypeBoxValueChanged(int newCounter)
{
   osgParticle::Counter* counter = mLayers[mLayerIndex].mModularEmitter->getCounter();

   switch(newCounter)
   {
   case 0:
      if(!IS_A(counter, osgParticle::RandomRateCounter*))
      {
         mLayers[mLayerIndex].mModularEmitter->setCounter(new osgParticle::RandomRateCounter());
         UpdateRandomRatesValues();
      }
      break;
   }
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::RandomRateMinRateValueChanged(double newValue)
{
   osgParticle::RandomRateCounter* rrc =
      (osgParticle::RandomRateCounter*)mLayers[mLayerIndex].mModularEmitter->getCounter();
   osgParticle::rangef rateRange = rrc->getRateRange();
   rateRange.minimum = newValue;
   rrc->setRateRange(rateRange);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::RandomRateMaxRateValueChanged(double newValue)
{
   osgParticle::RandomRateCounter* rrc =
      (osgParticle::RandomRateCounter*)mLayers[mLayerIndex].mModularEmitter->getCounter();
   osgParticle::rangef rateRange = rrc->getRateRange();
   rateRange.maximum = newValue;
   rrc->setRateRange(rateRange);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::PlacerTypeBoxValueChanged(int newCounter)
{
   osgParticle::Placer* placer = mLayers[mLayerIndex].mModularEmitter->getPlacer();

   switch(newCounter)
   {
   case 0:
      if(!IS_A(placer, osgParticle::PointPlacer*))
      {
         mLayers[mLayerIndex].mModularEmitter->setPlacer(new osgParticle::PointPlacer());
         UpdatePointPlacerValues();
      }
      break;

   case 1:
      if (!IS_A(placer, osgParticle::SectorPlacer*))
      {
         mLayers[mLayerIndex].mModularEmitter->setPlacer(new osgParticle::SectorPlacer());
         //updateSectorPlacerParameters();
      }
      break;

   case 2:
      if (!IS_A(placer, osgParticle::SegmentPlacer*))
      {
         mLayers[mLayerIndex].mModularEmitter->setPlacer(new osgParticle::SegmentPlacer());
         //updateSegmentPlacerParameters();
      }
      break;

   case 3:
      if (!IS_A(placer, osgParticle::MultiSegmentPlacer*))
      {
         osgParticle::MultiSegmentPlacer* msp = new osgParticle::MultiSegmentPlacer();
         msp->addVertex(-1, 0, 0);
         msp->addVertex(1, 0, 0);
         mLayers[mLayerIndex].mModularEmitter->setPlacer(msp);
         //updateMultiSegmentPlacerParameters();
      }
      break;
   }
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::PointPlacerXValueChanged(double newValue)
{
   osgParticle::PointPlacer* pp =(osgParticle::PointPlacer*)mLayers[mLayerIndex].mModularEmitter->getPlacer();
   osg::Vec3 center = pp->getCenter();
   center[0] = newValue;
   pp->setCenter(center);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::PointPlacerYValueChanged(double newValue)
{
   osgParticle::PointPlacer* pp =(osgParticle::PointPlacer*)mLayers[mLayerIndex].mModularEmitter->getPlacer();
   osg::Vec3 center = pp->getCenter();
   center[1] = newValue;
   pp->setCenter(center);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::PointPlacerZValueChanged(double newValue)
{
   osgParticle::PointPlacer* pp =(osgParticle::PointPlacer*)mLayers[mLayerIndex].mModularEmitter->getPlacer();
   osg::Vec3 center = pp->getCenter();
   center[2] = newValue;
   pp->setCenter(center);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::MakeCompass()
{
   dtCore::Compass* compass = new dtCore::Compass(GetCamera());
   GetScene()->AddDrawable(compass);
   mpCompassTransform = (osg::MatrixTransform*)compass->GetOSGNode();
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::MakeGrids()
{
   const int numVertices = 2 * 2 * GRID_LINE_COUNT;
   osg::Vec3 vertices[numVertices];
   float length = (GRID_LINE_COUNT - 1) * GRID_LINE_SPACING;
   int ptr = 0;

   for(int i = 0; i < GRID_LINE_COUNT; ++i)
   {
      vertices[ptr++].set(-length / 2 + i * GRID_LINE_SPACING, length / 2, 0.0f);
      vertices[ptr++].set(-length / 2 + i * GRID_LINE_SPACING, -length / 2, 0.0f);
   }

   for (int i = 0; i < GRID_LINE_COUNT; ++i)
   {
      vertices[ptr++].set(length / 2, -length / 2 + i * GRID_LINE_SPACING, 0.0f);
      vertices[ptr++].set(-length / 2, -length / 2 + i * GRID_LINE_SPACING, 0.0f);
   }

   osg::Geometry* geometry = new osg::Geometry;
   geometry->setVertexArray(new osg::Vec3Array(numVertices, vertices));
   geometry->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINES, 0, numVertices));

   osg::Geode* geode = new osg::Geode;
   geode->addDrawable(geometry);
   geode->getOrCreateStateSet()->setMode(GL_LIGHTING, 0);

   mpXYGridTransform = new osg::MatrixTransform;
   mpXYGridTransform->addChild(geode);

   mpXZGridTransform = new osg::MatrixTransform;
   mpXZGridTransform->setMatrix(osg::Matrix::rotate(osg::PI_2, 1, 0, 0));

   mpXZGridTransform->addChild(geode);
   mpXZGridTransform->setNodeMask(0x0);

   mpYZGridTransform = new osg::MatrixTransform;
   mpYZGridTransform->setMatrix(osg::Matrix::rotate(osg::PI_2, 0, 1, 0));

   mpYZGridTransform->addChild(geode);
   mpYZGridTransform->setNodeMask(0x0);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::UpdateParticleTabsValues()
{
   // Particle UI
   emit LayerHiddenChanged(mLayers[mLayerIndex].mModularEmitter->isEnabled());
   emit AlignmentUpdated((int)mLayers[mLayerIndex].mParticleSystem->getParticleAlignment());
   emit ShapeUpdated(mLayers[mLayerIndex].mpParticle->getShape());
   emit LifeUpdated(mLayers[mLayerIndex].mpParticle->getLifeTime());
   emit RadiusUpdated(mLayers[mLayerIndex].mpParticle->getRadius());
   emit MassUpdated(mLayers[mLayerIndex].mpParticle->getMass());
   osgParticle::rangef sizeRange = mLayers[mLayerIndex].mpParticle->getSizeRange();
   emit SizeFromUpdated(sizeRange.minimum);
   emit SizeToUpdated(sizeRange.maximum);

   // Texture UI
   std::string textureFile = "";
   osg::StateSet* ss = mLayers[mLayerIndex].mParticleSystem->getStateSet();
   osg::StateAttribute* sa = ss->getTextureAttribute(0, osg::StateAttribute::TEXTURE);
   if(IS_A(sa, osg::Texture2D*))
   {
      osg::Texture2D* t2d = (osg::Texture2D*)sa;
      osg::Image* image = t2d->getImage();
      if (image != NULL)
      {
         textureFile = image->getFileName();
      }
   }
   osg::BlendFunc* blend = (osg::BlendFunc*)ss->getAttribute(osg::StateAttribute::BLENDFUNC);
   bool emissive = blend->getDestination() == osg::BlendFunc::ONE;
   osg::Material* material = (osg::Material*)ss->getAttribute(osg::StateAttribute::MATERIAL);
   bool lighting = material->getColorMode() == osg::Material::AMBIENT_AND_DIFFUSE;
   emit TextureUpdated(QString::fromStdString(textureFile), emissive, lighting);

   // Color UI
   osgParticle::rangev4 colorRange = mLayers[mLayerIndex].mpParticle->getColorRange();
   emit RFromUpdated(colorRange.minimum[0]);
   emit RToUpdated(colorRange.maximum[0]);
   emit GFromUpdated(colorRange.minimum[1]);
   emit GToUpdated(colorRange.maximum[1]);
   emit BFromUpdated(colorRange.minimum[2]);
   emit BToUpdated(colorRange.maximum[2]);
   osgParticle::rangef alphaRange = mLayers[mLayerIndex].mpParticle->getAlphaRange();
   emit AFromUpdated(alphaRange.minimum);
   emit AToUpdated(alphaRange.maximum);

   // Emitter UI
   emit EmitterLifeUpdated(mLayers[mLayerIndex].mModularEmitter->getLifeTime());
   emit EmitterStartUpdated(mLayers[mLayerIndex].mModularEmitter->getStartTime());
   emit EmitterResetUpdated(mLayers[mLayerIndex].mModularEmitter->getResetTime());
   emit EndlessLifetimeUpdated(mLayers[mLayerIndex].mModularEmitter->isEndless());
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::UpdateCounterTabsValues()
{
   osgParticle::Counter* counter = mLayers[mLayerIndex].mModularEmitter->getCounter();
   if(IS_A(counter, osgParticle::RandomRateCounter*))
   {
      emit CounterTypeBoxUpdated(0);
      UpdateRandomRatesValues();
   }
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::UpdateRandomRatesValues()
{
   osgParticle::RandomRateCounter* rrc =
      (osgParticle::RandomRateCounter*)mLayers[mLayerIndex].mModularEmitter->getCounter();
   osgParticle::rangef rateRange = rrc->getRateRange();
   emit RandomRateMinRateUpdated(rateRange.minimum);
   emit RandomRateMaxRateUpdated(rateRange.maximum);
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::UpdatePlacerTabsValues()
{
   osgParticle::Placer* placer = mLayers[mLayerIndex].mModularEmitter->getPlacer();
   if(IS_A(placer, osgParticle::PointPlacer*))
   {
      emit PlacerTypeBoxUpdated(0);
      UpdatePointPlacerValues();
   }
   else if(IS_A(placer, osgParticle::SectorPlacer*))
   {
      emit PlacerTypeBoxUpdated(1);
   }
   else if(IS_A(placer, osgParticle::SegmentPlacer*))
   {
      emit PlacerTypeBoxUpdated(2);
   }
   else if(IS_A(placer, osgParticle::MultiSegmentPlacer*))
   {
      emit PlacerTypeBoxUpdated(3);
   }
}

////////////////////////////////////////////////////////////////////////////////
void ParticleViewer::UpdatePointPlacerValues()
{
   osgParticle::PointPlacer* pp =(osgParticle::PointPlacer*)mLayers[mLayerIndex].mModularEmitter->getPlacer();
   osg::Vec3 center = pp->getCenter();
   emit PointPlacerXUpdated(center[0]);
   emit PointPlacerYUpdated(center[1]);
   emit PointPlacerZUpdated(center[2]);
}

////////////////////////////////////////////////////////////////////////////////
